"""Add PJSIP Endpoint IP Access Control options

Revision ID: 6be31516058d
Revises: 81b01a191a46
Create Date: 2016-05-03 14:57:12.538179

"""

# revision identifiers, used by Alembic.
revision = '6be31516058d'
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
    with op.batch_alter_table('ps_endpoints') as batch_op:
        batch_op.drop_column('contact_acl')
    with op.batch_alter_table('ps_endpoints') as batch_op:
        batch_op.drop_column('contact_permit')
    with op.batch_alter_table('ps_endpoints') as batch_op:
        batch_op.drop_column('contact_deny')
    with op.batch_alter_table('ps_endpoints') as batch_op:
        batch_op.drop_column('acl')
    with op.batch_alter_table('ps_endpoints') as batch_op:
        batch_op.drop_column('permit')
    with op.batch_alter_table('ps_endpoints') as batch_op:
        batch_op.drop_column('deny')
