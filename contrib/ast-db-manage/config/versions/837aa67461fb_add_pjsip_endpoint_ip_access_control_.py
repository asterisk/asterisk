"""Add PJSIP Endpoint IP Access Control options

Revision ID: 837aa67461fb
Revises: 8d478ab86e29
Create Date: 2016-04-27 16:26:59.381117

"""

# revision identifiers, used by Alembic.
revision = '837aa67461fb'
down_revision = '8d478ab86e29'

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
