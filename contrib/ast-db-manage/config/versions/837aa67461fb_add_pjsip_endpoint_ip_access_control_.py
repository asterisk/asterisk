"""Add PJSIP Endpoint IP Access Control options

Revision ID: c849057214c6
Revises: 81b01a191a46
Create Date: 2016-05-06 13:54:33.742288

"""

# revision identifiers, used by Alembic.
revision = 'c849057214c6'
down_revision = '81b01a191a46'

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
