"""Add PJSIP Endpoint IP Access Control options

Revision ID: d7e3c73eb2bf
Revises: 6be31516058d
Create Date: 2016-05-13 12:45:45.071871

"""

# revision identifiers, used by Alembic.
revision = 'd7e3c73eb2bf'
down_revision = '6be31516058d'

from alembic import op
import sqlalchemy as sa


def upgrade():
    op.add_column('ps_endpoints', sa.Column('deny', sa.String(95)))
    op.add_column('ps_endpoints', sa.Column('permit', sa.String(95)))
    op.add_column('ps_endpoints', sa.Column('acl', sa.String(40)))
    op.add_column('ps_endpoints', sa.Column('contact_deny', sa.String(95)))
    op.add_column('ps_endpoints', sa.Column('contact_permit', sa.String(95)))
    op.add_column('ps_endpoints', sa.Column('contact_acl', sa.String(40)))


def downgrade():
    op.drop_column('ps_endpoints', 'contact_acl')
    op.drop_column('ps_endpoints', 'contact_permit')
    op.drop_column('ps_endpoints', 'contact_deny')
    op.drop_column('ps_endpoints', 'acl')
    op.drop_column('ps_endpoints', 'permit')
    op.drop_column('ps_endpoints', 'deny')
