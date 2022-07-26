"""add security_negotiation and security_mechanisms to endpoint

Revision ID: 417c0247fd7e
Revises: 539f68bede2c
Create Date: 2022-08-08 15:35:31.416964

"""

# revision identifiers, used by Alembic.
revision = '417c0247fd7e'
down_revision = '539f68bede2c'

from alembic import op
import sqlalchemy as sa
from sqlalchemy.dialects.postgresql import ENUM

SECURITY_NEGOTIATION_NAME = 'security_negotiation_values'
SECURITY_NEGOTIATION_VALUES = ['no', 'mediasec']

def upgrade():
    context = op.get_context()

    if context.bind.dialect.name == 'postgresql':
        security_negotiation_values = ENUM(*SECURITY_NEGOTIATION_VALUES, name=SECURITY_NEGOTIATION_NAME)
        security_negotiation_values.create(op.get_bind(), checkfirst=False)

    op.add_column('ps_endpoints', sa.Column('security_negotiation',
        ENUM(*SECURITY_NEGOTIATION_VALUES, name=SECURITY_NEGOTIATION_NAME, create_type=False)))
    op.add_column('ps_endpoints', sa.Column('security_mechanisms', sa.String(512)))

    op.add_column('ps_registrations', sa.Column('security_negotiation',
        ENUM(*SECURITY_NEGOTIATION_VALUES, name=SECURITY_NEGOTIATION_NAME, create_type=False)))
    op.add_column('ps_registrations', sa.Column('security_mechanisms', sa.String(512)))

def downgrade():
    context = op.get_context()

    if context.bind.dialect.name == 'mssql':
        op.drop_constraint('ck_ps_endpoints_security_negotiation_security_negotiation_values', 'ps_endpoints')
        op.drop_constraint('ck_ps_registrations_security_negotiation_security_negotiation_values', 'ps_registrations')

    op.drop_column('ps_endpoints', 'security_negotiation')
    op.drop_column('ps_endpoints', 'security_mechanisms')
    op.drop_column('ps_registrations', 'security_negotiation')
    op.drop_column('ps_registrations', 'security_mechanisms')

    if context.bind.dialect.name == 'postgresql':
        enum = ENUM(*SECURITY_NEGOTIATION_VALUES, name=SECURITY_NEGOTIATION_NAME)
        enum.drop(op.get_bind(), checkfirst=False)